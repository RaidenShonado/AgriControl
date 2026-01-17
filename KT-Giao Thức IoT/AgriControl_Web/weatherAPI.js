/**
 * API thời tiết từ Open-Meteo
 * Không cần API key, miễn phí 100%
 */

// Tọa độ các khu vực nông nghiệp (Việt Nam)
const locations = {
  khuA: {
    name: 'Khu A (TP.HCM)',
    latitude: 10.8231,
    longitude: 106.6297
  },
  khuB: {
    name: 'Khu B (Long An)',
    latitude: 10.5746,
    longitude: 106.4162
  },
  khuC: {
    name: 'Khu C (Tiền Giang)',
    latitude: 10.3667,
    longitude: 106.3667
  }
};

/**
 * Lấy dữ liệu thời tiết từ API Open-Meteo
 * @param {string} area - Khu vực (khuA, khuB, khuC)
 * @returns {Promise<Object>} - Dữ liệu thời tiết
 */
async function getWeatherData(area = 'khuA') {
  try {
    const location = locations[area];
    if (!location) {
      throw new Error(`Invalid area: ${area}`);
    }

    const apiUrl = `https://api.open-meteo.com/v1/forecast?latitude=${location.latitude}&longitude=${location.longitude}&hourly=temperature_2m,relative_humidity_2m,precipitation,weather_code,wind_speed_10m&daily=temperature_2m_max,temperature_2m_min,precipitation_sum,weather_code&timezone=Asia%2FHo_Chi_Minh`;

    const response = await fetch(apiUrl);
    if (!response.ok) {
      throw new Error(`HTTP error! status: ${response.status}`);
    }

    const data = await response.json();
    return {
      area: area,
      location: location,
      data: data,
      timestamp: new Date().toISOString()
    };
  } catch (error) {
    console.error(`Error fetching weather data for ${area}:`, error);
    return null;
  }
}

/**
 * Lấy nhiệt độ hiện tại
 * @param {string} area - Khu vực
 * @returns {Promise<number>} - Nhiệt độ (°C)
 */
async function getCurrentTemperature(area = 'khuA') {
  const weatherData = await getWeatherData(area);
  if (weatherData && weatherData.data.hourly.temperature_2m.length > 0) {
    return weatherData.data.hourly.temperature_2m[0];
  }
  return null;
}

/**
 * Lấy độ ẩm hiện tại
 * @param {string} area - Khu vực
 * @returns {Promise<number>} - Độ ẩm (%)
 */
async function getCurrentHumidity(area = 'khuA') {
  const weatherData = await getWeatherData(area);
  if (weatherData && weatherData.data.hourly.relative_humidity_2m.length > 0) {
    return weatherData.data.hourly.relative_humidity_2m[0];
  }
  return null;
}

/**
 * Lấy tốc độ gió hiện tại
 * @param {string} area - Khu vực
 * @returns {Promise<number>} - Tốc độ gió (km/h)
 */
async function getCurrentWindSpeed(area = 'khuA') {
  const weatherData = await getWeatherData(area);
  if (weatherData && weatherData.data.hourly.wind_speed_10m.length > 0) {
    return weatherData.data.hourly.wind_speed_10m[0];
  }
  return null;
}

/**
 * Lấy dự báo hàng ngày
 * @param {string} area - Khu vực
 * @returns {Promise<Array>} - Mảng dự báo cho 7 ngày
 */
async function getDailyForecast(area = 'khuA') {
  try {
    const weatherData = await getWeatherData(area);
    if (!weatherData || !weatherData.data.daily) {
      return [];
    }

    const daily = weatherData.data.daily;
    const forecast = [];

    for (let i = 0; i < daily.time.length; i++) {
      forecast.push({
        date: daily.time[i],
        tempMax: daily.temperature_2m_max[i],
        tempMin: daily.temperature_2m_min[i],
        precipitation: daily.precipitation_sum[i],
        weatherCode: daily.weather_code[i]
      });
    }

    return forecast;
  } catch (error) {
    console.error('Error fetching daily forecast:', error);
    return [];
  }
}

/**
 * Lấy dự báo hàng giờ
 * @param {string} area - Khu vực
 * @param {number} hours - Số giờ dự báo (mặc định 24)
 * @returns {Promise<Array>} - Mảng dự báo hàng giờ
 */
async function getHourlyForecast(area = 'khuA', hours = 24) {
  try {
    const weatherData = await getWeatherData(area);
    if (!weatherData || !weatherData.data.hourly) {
      return [];
    }

    const hourly = weatherData.data.hourly;
    const forecast = [];

    for (let i = 0; i < Math.min(hours, hourly.time.length); i++) {
      forecast.push({
        time: hourly.time[i],
        temperature: hourly.temperature_2m[i],
        humidity: hourly.relative_humidity_2m[i],
        precipitation: hourly.precipitation[i],
        windSpeed: hourly.wind_speed_10m[i],
        weatherCode: hourly.weather_code[i]
      });
    }

    return forecast;
  } catch (error) {
    console.error('Error fetching hourly forecast:', error);
    return [];
  }
}

/**
 * Dịch mã thời tiết sang tiếng Việt
 * @param {number} code - Mã thời tiết
 * @returns {string} - Mô tả thời tiết
 */
function getWeatherDescription(code) {
  const weatherCodes = {
    0: 'Trời quang',
    1: 'Hầu như quang',
    2: 'Một phần u ám',
    3: 'U ám',
    45: 'Sương mù',
    48: 'Sương mù cấp',
    51: 'Mưa nhẹ',
    53: 'Mưa vừa',
    55: 'Mưa nặng',
    61: 'Mưa nhẹ',
    63: 'Mưa vừa',
    65: 'Mưa nặng',
    71: 'Tuyết nhẹ',
    73: 'Tuyết vừa',
    75: 'Tuyết nặng',
    77: 'Hạt tuyết',
    80: 'Cơn mưa nhẹ',
    81: 'Cơn mưa vừa',
    82: 'Cơn mưa nặng',
    85: 'Tuyết rơi nhẹ',
    86: 'Tuyết rơi nặng',
    95: 'Bão có sét nhẹ',
    96: 'Bão có sét vừa',
    99: 'Bão có sét nặng'
  };
  return weatherCodes[code] || 'Không xác định';
}

/**
 * Định dạng dữ liệu thời tiết để hiển thị
 * @param {string} area - Khu vực
 * @returns {Promise<Object>} - Dữ liệu đã định dạng
 */
async function getFormattedWeatherData(area = 'khuA') {
  try {
    const weatherData = await getWeatherData(area);
    if (!weatherData) {
      return null;
    }

    const hourly = weatherData.data.hourly;
    const daily = weatherData.data.daily;

    return {
      area: area,
      location: locations[area].name,
      current: {
        temperature: hourly.temperature_2m[0],
        humidity: hourly.relative_humidity_2m[0],
        windSpeed: hourly.wind_speed_10m[0],
        precipitation: hourly.precipitation[0],
        weatherDescription: getWeatherDescription(hourly.weather_code[0])
      },
      today: {
        tempMax: daily.temperature_2m_max[0],
        tempMin: daily.temperature_2m_min[0],
        precipitation: daily.precipitation_sum[0]
      },
      timestamp: new Date().toISOString()
    };
  } catch (error) {
    console.error('Error formatting weather data:', error);
    return null;
  }
}

/**
 * Cập nhật dữ liệu thời tiết định kỳ
 * @param {Array} areas - Mảng khu vực
 * @param {number} intervalMs - Khoảng thời gian cập nhật (ms)
 */
function startWeatherUpdates(areas = ['khuA', 'khuB', 'khuC'], intervalMs = 300000) {
  console.log(`Starting weather updates every ${intervalMs}ms...`);
  
  setInterval(async () => {
    for (const area of areas) {
      const weatherData = await getFormattedWeatherData(area);
      console.log(`[${new Date().toLocaleTimeString()}] Weather ${area}:`, weatherData);
      
      // Cập nhật UI nếu hàm tồn tại
      if (window.updateWeatherUI) {
        window.updateWeatherUI(area, weatherData);
      }
    }
  }, intervalMs);
}

// Export functions
if (typeof module !== 'undefined' && module.exports) {
  module.exports = {
    getWeatherData,
    getCurrentTemperature,
    getCurrentHumidity,
    getCurrentWindSpeed,
    getDailyForecast,
    getHourlyForecast,
    getWeatherDescription,
    getFormattedWeatherData,
    startWeatherUpdates,
    locations
  };
}
